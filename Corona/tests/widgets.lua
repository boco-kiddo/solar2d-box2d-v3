-- Small touch-based button used by the test app and the test scenes.
-- Buttons react on "ended" inside the button, take the touch focus while pressed and always return true,
-- so scene objects underneath (touch catchers, draggable bodies) never see the touch.
local M = {}

function M.button( parent, label, x, y, w, onPress, opts )
	opts = opts or {}
	local g = display.newGroup()
	parent:insert( g )
	g.x, g.y = x, y
	local h = opts.height or 22
	local bg = display.newRoundedRect( g, 0, 0, w, h, 4 )
	local c = opts.color or { 0.22, 0.27, 0.38 }
	bg:setFillColor( c[1], c[2], c[3], opts.alpha or 0.92 )
	bg.strokeWidth = 1
	bg:setStrokeColor( 1, 1, 1, 0.15 )
	g.label = display.newText{ parent = g, text = label, x = 0, y = 0, font = native.systemFont, fontSize = opts.fontSize or 11 }
	function g:setLabel( t ) g.label.text = t end
	bg:addEventListener( "touch", function( e )
		if e.phase == "began" then
			display.getCurrentStage():setFocus( bg )
			bg.pressed = true
			g.alpha = 0.6
			if opts.onDown then opts.onDown( g ) end
		elseif bg.pressed then
			if e.phase == "ended" or e.phase == "cancelled" then
				display.getCurrentStage():setFocus( nil )
				bg.pressed = false
				g.alpha = 1
				if opts.onUp then opts.onUp( g ) end
				local bx, by = bg:contentToLocal( e.x, e.y )
				if e.phase == "ended" and math.abs( bx ) <= w / 2 and math.abs( by ) <= h / 2 and onPress then
					onPress( g )
				end
			end
		end
		return true
	end )
	bg:addEventListener( "tap", function() return true end )
	return g
end

return M
